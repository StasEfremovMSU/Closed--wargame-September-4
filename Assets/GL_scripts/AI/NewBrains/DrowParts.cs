using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class DrowParts : MonoBehaviour {

	int count;
	bool stop;

	public GameObject PartAngl;

	void Start () {
		count = 0;
		stop = false;
	}

	void Update ()
	{
		if (stop == false)
		if (count < 10) {
			count++;
		} else {
			for (int g = 0; g < AI.Gen_stat.Count; g++) {
				for (int h = 0; h < AI.Gen_stat [g].parts.Count; h++) {
					Instantiate (PartAngl, new Vector3 (AI.Gen_stat [g].parts [h].points [0].x, 2, AI.Gen_stat [g].parts [h].points [0].y), Quaternion.identity);
					Instantiate (PartAngl, new Vector3 (AI.Gen_stat [g].parts [h].points [1].x, 2, AI.Gen_stat [g].parts [h].points [1].y), Quaternion.identity);
					Instantiate (PartAngl, new Vector3 (AI.Gen_stat [g].parts [h].points [2].x, 2, AI.Gen_stat [g].parts [h].points [2].y), Quaternion.identity);
					Instantiate (PartAngl, new Vector3 (AI.Gen_stat [g].parts [h].points [3].x, 2, AI.Gen_stat [g].parts [h].points [3].y), Quaternion.identity);
					stop = true;
				}
			}
		}
	}
}
