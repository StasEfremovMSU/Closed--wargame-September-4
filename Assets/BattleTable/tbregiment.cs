using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.AI;


public class tbregiment : MonoBehaviour {

	bool temp;
	NavMeshAgent main;
	bool bag;
	/// <summary>
	// Номер команды
	/// </summary>
	public int command;
	bool active;
	private bool stateMoving;
	public GameObject newPos;
	/// <summary>
	/// Номер полка
	/// </summary>
	public int number;
	public LineRenderer linePref;
	public LineRenderer line;
	public GameObject Pref;

    public Vector3 position;
    public float angle;
    public int fline;
    public int countsol;
    public float fline_dist;
    public string buf;

    void Start()
    {
		bag = false;
		stateMoving = false;
		temp = false;
		//num = 0;
		main = GetComponent<NavMeshAgent>();
		active = false;
        angle = new float();
        buf = "";
    }

	void Update()
	{
		RaycastHit hit;

		if (tbstatus.mov)
		{
			if ((transform.position - newPos.transform.position).magnitude > 1.5) {
				main.SetDestination (newPos.transform.position);
			} else {//Debug.Log ("11111111111");
				Debug.Log (command);
				if (command == 1) {
					//Debug.Log ("fghjk");
					gameObject.transform.LookAt ( new Vector3(0,transform.position.y,-100));
				}
			}
		}

		if (tbstatus.rotation == false)
		if (stateMoving == false)
		{
			if (Input.GetMouseButtonDown(0))
			{
				Ray ray = Camera.main.ScreenPointToRay(Input.mousePosition);

				Physics.Raycast(ray, out hit);
				if (hit.transform.gameObject.GetComponent<tbregiment>())
				{
					if (hit.transform.gameObject.GetComponent<tbregiment>().number == number)
					{
						if (bag == false)
						if (stateMoving == false)
						{
							newPos = Instantiate(Pref, transform.position, Quaternion.identity);
							newPos.GetComponent<newRegPos>().num = number;
							bag = true;
							line = Instantiate(linePref, new Vector3(0, 0.1f, 0), Quaternion.identity);
							line.GetComponent<LineRenderer>().SetPosition(0, transform.position);

							stateMoving = true;
							newPos.GetComponent<CapsuleCollider>().enabled = false;
							SetActive();
						}
					}
				}

				if (hit.transform.gameObject.GetComponent<newRegPos>())
				{
					// если нажали на существующую конечную точку
					if (hit.transform.gameObject.GetComponent<newRegPos>().num == number)
					{                    
						stateMoving = true;
						newPos.GetComponent<CapsuleCollider>().enabled = false;

					}
				}
			}
		}
		else
		{

			MovingNewPos();
		}
	}

	void MakeNewPos()
	{

	}

	void MovingNewPos()
	{
		RaycastHit hit;
		Ray rayh = Camera.main.ScreenPointToRay(Input.mousePosition);
		Physics.Raycast(rayh, out hit);
		newPos.transform.LookAt(hit.point);
		newPos.transform.position = new Vector3(hit.point.x, hit.point.y, hit.point.z);
		line.GetComponent<LineRenderer>().SetPosition(1, new Vector3(hit.point.x, hit.point.y, hit.point.z));
		if (Input.GetMouseButtonDown(0))
		{
			stateMoving = false;
			newPos.GetComponent<CapsuleCollider>().enabled = true;
			temp = true;
			SetDesActive();
		}
	}


	void SetActive()
	{
		gameObject.GetComponent<Renderer>().material.color = Color.blue;
		//newPos.GetComponent<Renderer>().material.color = Color.blue;
		active = true;
	}

	void SetDesActive()
	{
		active = false;
		gameObject.GetComponent<Renderer>().material.color = Color.red;
		//newPos.GetComponent<Renderer>().material.color = Color.red;
	}

    public void make(temm t)
    {
        buf = "";
        //angle = new float();
        number = int.Parse( t.x[1]);
        command = int.Parse(t.x[0]);
		//Debug.Log (command);
        position.x = float.Parse(t.x[2])/10;
		Debug.Log (position.x);
        position.y = float.Parse(t.x[3]);
        position.z = float.Parse(t.x[4])/10;
        angle = float.Parse(t.x[5]);
        fline = int.Parse(t.x[7]);
        countsol = int.Parse(t.x[6]);
        fline_dist = 3f;//float.Parse(t.x[0]);
        
        for (int i = 8; i < t.x.Length; i++)
        {
            buf += t.x[i];
            if (i !=(t.x.Length-1))
                buf += ",";
        }
        //Debug.Log(buf);
    }
}
