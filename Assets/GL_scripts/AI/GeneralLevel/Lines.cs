using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class Lines  {


   // public List <point>;

	/// <summary>
	///  На старте
	/// </summary>
	public void Start () {
        /// Для каждого генерала
        for (int k = 0; k < mainStatic.generalMod.Count; k++)
        {
            switch (mainStatic.generalMod[k].totalTactic)
            {
                /// Случай полного наступления, годится для маленьких армий
                case (5):

                    break;




            }


        }
		
	}
	
	// Update is called once per frame
	void Update () {
		
	}
}

public class chain
{

    public int NumLines;
    
    public int TypePos;


}


public class point
{
    float angle;

    Vector3 position;

    float weight;

    float command;

    List<regStat> regiments;

}
